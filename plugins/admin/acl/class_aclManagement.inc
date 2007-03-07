<?php
/*
   This code is part of GOsa (https://gosa.gonicus.de)
   Copyright (C) 2003-2006 - Cajus Pollmeier <pollmeier@gonicus.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

require_once("tabs_acl_role.inc");
require_once("tabs_acl.inc");

class aclManagement extends plugin
{
  /* Plugin definitions */
  var $plHeadline     = "ACLs";
  var $plDescription  = "This does something";

  /* Dialog attributes */
  var $acltabs              = NULL;
  var $ui                   = NULL;
  var $acl                  = "";
  var $DivListACL           = NULL;

  var $CopyPasteHandler;

  function aclManagement($config, $ui)
  {
    /* Save configuration for internal use */
    $this->config = $config;
    $this->ui     = $ui;

    /* Copy & Paste enabled ?*/
    if((isset($this->config->data['MAIN']['ENABLECOPYPASTE']))&&(preg_match("/true/i",$this->config->data['MAIN']['ENABLECOPYPASTE']))){
      $this->CopyPasteHandler = new CopyPasteHandler($this->config);
    }

    /* Creat dialog object */
    $this->DivListACL = new divListACL($this->config,$this);
  }


  function execute()
  {
    /* Call parent execute */
    plugin::execute();

    $_SESSION['LOCK_VARS_TO_USE'] = array("/^list/","/^id_/");

    $smarty       = get_smarty();                 // Smarty instance
    $s_action     = "";                           // Contains the action to be taken
    $s_entry      = "";                           // The value for s_action

    /* Edit entry button pressed? */
    if( isset($_GET['act']) && $_GET['act'] == "list_edit_entry" ){
      $s_action= "edit";
      $s_entry= validate($_GET['id']);
    }

    /* Edit entry button pressed? */
    if( isset($_GET['act']) && $_GET['act'] == "list_edit_role" ){
      $s_action= "edit_role";
      $s_entry= validate($_GET['id']);
    }

    $types = array( 
                    "del"       =>"^list_acl_del",
                    "edit"      =>"^list_acl_edit",
                    "del_role"  =>"^list_acl_role_del",
                    "edit_role" =>"^list_acl_role_edit",
                    "copy"      =>"^copy",
                    "cut"       =>"^cut",
                    "editPaste" =>"^editPaste",
                    "addrole"   =>"^new_acl_role");

    /* Test relevant POST values */  
    foreach($_POST as $key => $val){

      /* Get every possible POST combination and set s_action/s_entry accordingly */
      foreach($types as $act => $name){

        if (preg_match("/".$name.".*/", $key)){
          $s_action= $act;
          $s_entry= preg_replace("/".$name."_/i", "", $key);
        }
      }
    } 

    /* Remove coordinate prefix from POST, required by some browsers */
    $s_entry= preg_replace("/_.$/", "", $s_entry);

    /* Seperate possibly encoded tab and entry, default to tab "acl" */
    if(preg_match("/.*-.*/", $s_entry)){
      $s_tab= preg_replace("/^[^-]*-/i", "" ,$s_entry);
      $s_entry= preg_replace("/-[^-]*$/i", "", $s_entry);
    }else{
      $s_tab= "generic";
    }
   
 
    /********************
      Copy & Paste Handling  ...
     ********************/

    /* Only perform copy&paste requests if it is enabled
     */
    if($this->CopyPasteHandler){
      if($str = $this->copyPasteHandling($s_action,$s_entry)){
        return $str;
      }
    }


    /********************
     * Add new Role entry 
     ********************/

    if(($s_action == "addrole") && (!isset($this->acltabs->config))){

      /* Get 'dn' from posted acl, must be unique */
      $this->dn= "new";

      /* Check permissions */
      if(preg_match("/c/",$this->ui->get_permissions($this->DivListACL->selectedBase,"acl/acl"))){

        /* Register acltabs to trigger edit dialog */
        $this->acltabs= new aclroletab($this->config, NULL,$this->dn);
        $this->acltabs->set_acl_base($this->DivListACL->selectedBase);
      }else{
        print_red(_("You are not allowed to create a new role."));
      }
    }

    /********************
      Edit existing entry 
     ********************/

    if (($s_action=="edit" || $s_action=="edit_role") && (!isset($this->acltabs->config))){

      /* Get 'dn' from posted acl, must be unique */
      $this->dn= $this->list[trim($s_entry)]['dn'];

      /* Check permissions */
      if(preg_match("/r/",$this->ui->get_permissions($this->dn,"acl/acl"))){
      
        /* Check locking, save current plugin in 'back_plugin', so
           the dialog knows where to return. */
        if (($acl= get_lock($this->dn)) != ""){
          return(gen_locked_message ($acl, $this->dn));
        }

        /* Lock the current entry, so everyone will get the above dialog */
        add_lock ($this->dn, $this->ui->dn);

        /* Register acltabs to trigger edit dialog */ 
        if($s_action=="edit_role"){
          $this->acltabs= new aclroletab($this->config, NULL,$this->dn);
          $this->acltabs-> set_acl_base($this->dn);
        }else{
          $this->acltabs= new acltab($this->config, NULL,$this->dn);
          $this->acltabs-> set_acl_base($this->dn);
        }

        /* Set ACL and move DN to the headline */
        $_SESSION['objectinfo']= $this->dn;
      }else{
        print_red(_("You are not allowed to edit this acl."));
      }
    }


    /********************
      Edit canceled 
     ********************/
    if(isset($_POST['delete_lock'])){
      del_lock ($this->dn);
      unset($_SESSION['objectinfo']);
    }

    /* Reset all relevant data, if we get a _cancel request */
    if (isset($_POST['edit_cancel'])){
      if (isset($this->acltabs)){
        del_lock ($this->dn);
        unset ($this->acltabs);
      }
      $this->acltabs= NULL;
      unset ($_SESSION['objectinfo']);
    }


    /********************
      Delete entry requested, display confirm dialog
     ********************/

    /* Remove acl was requested */
    if ($s_action=="del"){

      /* Get 'dn' from posted acl */
      $this->dn= $this->list[trim($s_entry)]['dn'];

      /* Check permissions */
      if(preg_match("/d/",$this->ui->get_permissions($this->dn,"acl/acl"))){
        /* Check locking, save current plugin in 'back_plugin', so
           the dialog knows where to return. */
        if (($acl= get_lock($this->dn)) != ""){
          $_SESSION['LOCK_VARS_TO_USE'] = array("/^list_acl_edit/","/^list_acl_del/","/^id_/");
          return(gen_locked_message ($acl, $this->dn));
        }

        /* Lock the current entry, so nobody will edit it during deletion */
        add_lock ($this->dn, $this->ui->dn);
        $smarty->assign("info", sprintf(_("You're about to delete the acl %s."), @LDAP::fix($this->dn)));
        return($smarty->fetch(get_template_path('remove.tpl', TRUE)));
      } else {

        /* Obviously the acl isn't allowed to delete. Show message and
           clean session. */
        print_red (_("You are not allowed to delete this acl!"));
      }
    }


    /********************
      Delete entry confirmed 
     ********************/

    /* Confirmation for deletion has been passed. Acl should be deleted. */
    if (isset($_POST['delete_acl_confirmed'])){

      /* Check permissions */
      if(preg_match("/d/",$this->ui->get_permissions($this->dn,"acl/acl"))){

        /* Delete request is permitted, perform LDAP action */
        $this->acltabs= new acl($this->config, NULL,$this->dn);
        $this->acltabs->delete ();
        gosa_log ("ACL object '".$this->dn."' has been removed");
        unset ($this->acltabs);
        $this->acltabs= NULL;
      } else {

        /* Normally this shouldn't be reached, send some extra
           logs to notify the administrator */
        print_red (_("You are not allowed to delete this acl!"));

        if(isset($this->ui->uid)){
          gosa_log ("Warning: '".$this->ui->uid."' tried to trick acl deletion.");
        }
      }

      /* Remove lock file after successfull deletion */
      del_lock ($this->dn);
    }

    
    /********************
      Delete entry Canceled 
     ********************/

    /* Delete acl canceled? */
    if (isset($_POST['delete_cancel'])){
      del_lock ($this->dn);
    }


    /********************
      Edit entry finished (Save) 
     ********************/

    /* Finish acl edit is triggered by the tabulator dialog, so
       the acl wants to save edited data. Check and save at this
       point. */
    if ((isset($_POST['edit_finish']) || isset($_POST['edit_apply'])) && (isset($this->acltabs->config))){

      /* Check tabs, will feed message array */
      $this->acltabs->save_object();
      $message= $this->acltabs->check();

      /* Save, or display error message? */
      if (count($message) == 0){

        /* Save acl data to ldap */
        if($this->acltabs->save() == 1){
          gosa_log ("ACL object '".$this->dn."' saving failed.");
          return;
        }
        gosa_log ("ACL object '".$this->dn."' has been saved");

        if (!isset($_POST['edit_apply'])){

          /* ACl has been saved successfully, remove lock from LDAP. */
          if ($this->dn != "new"){
            del_lock ($this->dn);
          }

          unset ($this->acltabs);
          $this->acltabs= NULL;
          unset ($_SESSION['objectinfo']);
        }
      } else {
        /* Ok. There seem to be errors regarding to the tab data,
           show message and continue as usual. */
        show_errors($message);
      }
    }


    /********************
      Display subdialog 
     ********************/


    /* Show tab dialog if object is present */
    if(isset($this->acltabs->config)){
      
      /* Save object */
      $this->acltabs->save_object();
      $display= $this->acltabs->execute();

      /* Don't show buttons if tab dialog requests this */
      if(isset($this->acltabs)){

        /* Skip displaying save/cancel if there is a sub dialog open */
        if (!isset($this->acltabs->dialog) || !$this->acltabs->dialog){
          $display.= "<p style=\"text-align:right\">\n";
//          $display.= "<input type=submit name=\"edit_finish\" style=\"width:80px\" value=\""._("Ok")."\">\n";
          $display.= "&nbsp;\n";

          /* Skip Apply if it is a new entry */
          #if ($this->dn != "new"){
          #  $display.= "<input type=submit name=\"edit_apply\" value=\""._("Apply")."\">\n";
          #  $display.= "&nbsp;\n";
          #}

  //        $display.= "<input type=submit name=\"edit_cancel\" value=\""._("Cancel")."\">\n";
          $display.= "</p>";
        }
      }
      return ($display);
    }
    
    /* Check if there is a snapshot dialog open */
    $base = $this->DivListACL->selectedBase;
    if($str = $this->showSnapshotDialog($base,$this->get_used_snapshot_bases())){
      return($str);
    }
  
    /* Return rendered main page */
        /* Display dialog with system list */
    $this->DivListACL->parent = $this;
    $this->DivListACL->execute();

    /* Add departments if subsearch is disabled */
    if(!$this->DivListACL->SubSearch){
      $this->DivListACL->AddDepartments($this->DivListACL->selectedBase,3);
    }
    $this->reload();
    $this->DivListACL->setEntries($this->list);
    return($this->DivListACL->Draw());
  }


  function reload()
  {
    /* Get divlist informations from filter part */
    $Regex      = $this->DivListACL -> Regex;
    $SubSearch  = $this->DivListACL -> SubSearch;
    $base       = $_SESSION['CurrentMainBase']; 
    $Attrs      = array("ou","cn","description","gosaAclEntry","objectClass");
    $res        = array();
    $tmp        = array();                                    // Will contain temporary results 
    $ldap       = $this->config->get_ldap_link();
    $Filter     = "(&(objectClass=gosaACL)(gosaAclEntry=*)(|(cn=".$Regex.")(ou=".$Regex.")))";
    $FilterRoles= "(&(objectClass=gosaRole)(|(cn=".$Regex.")(ou=".$Regex.")))";

    /* Fetch following structures, this will be used if !$SubSearch */
    $fetch_this = array(
      "ME"      => array("TYPE" => "cat"    , "FLAGS" => GL_SIZELIMIT                ,"BASE"=>""),
      "SYSTEMS" => array("TYPE" => "search" , "FLAGS" => GL_SIZELIMIT | GL_SUBSEARCH ,"BASE"=>"ou=systems,"),
      "APPS"    => array("TYPE" => "search" , "FLAGS" => GL_SIZELIMIT | GL_SUBSEARCH ,"BASE"=>"ou=apps,"),
      "PEOPLE"  => array("TYPE" => "search" , "FLAGS" => GL_SIZELIMIT | GL_SUBSEARCH ,"BASE"=>get_people_ou()),
      "GROUPS"  => array("TYPE" => "search" , "FLAGS" => GL_SIZELIMIT | GL_SUBSEARCH ,"BASE"=>get_groups_ou()));

    /* Subsearch ? */
    if($SubSearch){

      /* Get all object in this base */
      $Flags        = GL_SIZELIMIT | GL_SUBSEARCH;
      $fetch_base   = $base;
      $tmp  = get_list($Filter,   "acl", $fetch_base, $Attrs, $Flags);
      $tmp2 = get_list($FilterRoles,  "acl", $fetch_base, $Attrs, $Flags);
      foreach($tmp as $entry){
        $res[] = $entry;
      }
      foreach($tmp2 as $entry){
        $res[] = $entry;
      }

    }else{

      $tmp_roles = get_list($FilterRoles,  "acl", "ou=aclroles,".$base, $Attrs,GL_SIZELIMIT);

      foreach($tmp_roles as $entry){
        $res[] = $entry;
      }

      /* Walk through all possible bases */
      foreach($fetch_this as $type => $data){

        /* Get requried attributes */
        $Flags        = $data['FLAGS'];
        $fetch_base   = $data['BASE'].$base;
        $Type         = $data['TYPE'];

        /* Check if method is cat or search */
        if($Type == "search"){
          $tmp = get_list($Filter, "acl", $fetch_base, $Attrs, $Flags);
          foreach($tmp as $entry){
            $res[] = $entry;
          }
        }else{
          $ldap->cat($fetch_base,$Attrs);
          if($ldap->count())          {
            $attrs = $ldap->fetch();
            $re2 = preg_replace("/\*/",".*",$Regex);
            if( (isset($attrs['cn'][0]) && preg_match("/".$re2."/i",$attrs['cn'][0]))
               |(isset($attrs['ou'][0]) && preg_match("/".$re2."/i",$attrs['ou'][0]))){

              $res[] = $attrs;
            }
          }
        }
      }
    }
    $this->list = $res;
  }


  /* Perform copy & paste requests
     If copy&paste is in progress this returns a dialog to fix required attributes
   */
  function copyPasteHandling($s_action,$s_entry)
  {
    /* Paste copied/cutted object in here
     */
    $ui = get_userinfo();
    if(($s_action == "editPaste") || ($this->CopyPasteHandler->stillOpen())){
      $this->CopyPasteHandler->save_object();
      $this->CopyPasteHandler->SetVar("base",$this->DivListGroup->selectedBase);
      if($str = $this->CopyPasteHandler->execute()){
        return( $str);
      };
      /* Ensure that the new object is shown in the list now */
      $this->reload();
    }


    /* Copy current object to CopyHandler
     */
    if($s_action == "copy"){

      $dn  = $this->list[trim($s_entry)]['dn'];
      $acl_all  = $ui->has_complete_category_acls($this->DivListACL->selectedBase,"acl") ;
      if(preg_match("/(c.*w|w.*c)/",$acl_all)){

        $this->CopyPasteHandler->Clear();
        $obj    = new acl($this->config, NULL, $dn, TRUE);
        $obj->set_acl_base($dn);
        $objNew = new acl($this->config, NULL,"new", TRUE);
        $objNew->set_acl_base($dn);

        $this->CopyPasteHandler->Copy($obj,$objNew);
      }else{
        print_red("You are not allowed to copy this entry.");
      }
    }

    /* Copy current object to CopyHandler
     */
    if($s_action == "cut"){

      $dn = $this->list[trim($s_entry)]['dn'];
      $acl_all  = $ui->has_complete_category_acls($this->DivListACL->selectedBase,"acl") ;
      if(preg_match("/(c.*w|w.*c)/",$acl_all)){

        $this->CopyPasteHandler->Clear();
        $obj= new acl($this->config,NULL, $dn, TRUE);
        $obj->set_acl_base($dn);
        $this->CopyPasteHandler->Cut($obj);
      }else{
        print_red("You are not allowed to cut this entry.");
      }
    }
  }


  function remove_lock()
  {
    /* Remove acl lock if a DN is marked as "currently edited" */
    if (isset($this->acltabs->dn)){
      del_lock ($this->acltabs->dn);
    }
  }


  function save_object()
  {
    /* Handle divlist filter && department selection*/
    if(!is_object($this->acltabs)){
      $this->DivListACL->save_object();
    }
  }

  /* A set of disabled and therefore overloaded functions. They are
     not needed in this class. */
  function remove_from_parent() { } 
  function check() { } 

  function save() {
    echo "SAVE ACL";
  } 

  function adapt_from_template($dn) { } 
  function password_change_needed() { } 


  /* Return departments, that will be included within snapshot detection */
  function get_used_snapshot_bases()
  {
    return(array());
  }  

} 
// vim:tabstop=2:expandtab:shiftwidth=2:filetype=php:syntax:ruler:
?>