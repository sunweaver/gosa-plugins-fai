<?php

class faiTemplateEntry extends plugin
{
  /* attribute list for save action */
  var $ignore_account= TRUE;
  var $attributes   = array("cn","description","FAItemplateFile","FAItemplatePath","FAImode","user","group","binary","FAIowner");
  var $objectclasses= array();

  var $orig_cn              = "";

  var $dn            = "";
  var $cn            = "";
  var $FAItemplateFile   = "";
  var $FAItemplatePath   = "";
  var $description   = "";
  var $status        = "new";
  var $FAImode       = "0640";
  var $FAIowner      = "root.root";
  var $user          = "root";
  var $group         = "root";
  var $binary        = false;
  var $parent        = NULL;
  var $FAIstate      = "";
  
  function faiTemplateEntry (&$config, $dn= NULL,$object=false)
  {
    plugin::plugin ($config, $dn);

    if((isset($object['cn'])) && (!empty($object['cn']))){
      $this->orig_cn= $object['cn'];
      $this->dn=$object['dn'];
      foreach($object as $name=>$value){
        $oname = $name;
        $this->$oname=$value;
      }

      if(isset($this->attrs['FAIstate'][0])){
        $this->FAIstate = $this->attrs['FAIstate'][0];
      }

    }else{
      $this->status = "new";
      $this->orig_cn= false;
    }

    $this->user = explode( '.', $this->FAIowner );
    $this->group = $this->user[1];
    $this->user = $this->user[0];

    session::set('binary',$this->FAItemplateFile);
    session::set('binarytype','octet-stream');
    session::set('binaryfile',basename($this->FAItemplatePath));

    if(!empty($this->dn) && $this->dn != "new"){
      $ldap = $this->config->get_ldap_link();
      session::set('binary',$ldap->get_attribute($this->dn,"FAItemplateFile"));
      $this->FAItemplateFile  = session::get('binary');
    }
    
    $this->FAImode= sprintf("%0.4s", $this->FAImode)." ";
  }


  function execute()
  {
    /* Call parent execute */
    plugin::execute();

    /* Fill templating stuff */
    $smarty     = get_smarty();
    $smarty->assign("rand", rand(0, 10000));
    $display = "";

    if(isset($_POST['TmpFileUpload']) && $this->acl_is_writeable("FAItemplateFile")){
      if($str=file_get_contents($_FILES['FAItemplateFile']['tmp_name'])){
        $this->FAItemplateFile = $str;

        /* If we don't have a filename set it from upload filename. */
        if( 0 == strlen( $this->FAItemplatePath )){
          $this->FAItemplatePath = $_FILES['FAItemplateFile']['name'];
        }

        session::set('binary',$this->FAItemplateFile);
        session::set('binarytype','octet-stream');
        session::set('binaryfile',basename($this->FAItemplatePath));
      }
    }
   
    /* File download requested */
    if(isset($_GET['getFAItemplate'])){
      send_binary_content($this->FAItemplateFile,$this->cn.".FAItemplate");
    }
 
    $status= _("no file uploaded yet");

    $bStatus = false; // Hide download icon on default 
    
    if(strlen($this->FAItemplateFile)){

      $status= sprintf(_("exists in database (size: %s bytes)"),strlen($this->FAItemplateFile));
      $bStatus = true;  // Display download icon 
    }
    $smarty->assign("status",$status);
    $smarty->assign("bStatus",$bStatus);

    /* Magic quotes GPC, escapes every ' " \, to solve some security risks 
     * If we post the escaped strings they will be escaped again
     */
    foreach($this->attributes as $attrs){
      if(get_magic_quotes_gpc()){
        $smarty->assign($attrs,stripslashes($this->$attrs));
      }else{
        $smarty->assign($attrs,($this->$attrs));
      }
    }

    /* Assign file modes */
    $tmode= "$this->FAImode ";
    foreach (array("s", "u", "g", "o") as $type){
      $current= substr($tmode, 0, 1);
      $tmode=   preg_replace("/^./", "", $tmode);
      $nr= 1;
      while ($nr < 5){
        if ($current & $nr){
          $smarty->assign($type.$nr, "checked");
        } else {
          $smarty->assign($type.$nr, "");
        }
        $nr+= $nr;
      }
    }

    $smarty->assign("FAItemplateFile","");

    foreach($this->attributes as $attr){
      $smarty->assign($attr."ACL",$this->getacl($attr,preg_match("/freeze/",$this->FAIstate)));
    }

    /* We now split cn/FAItemplatePath to make things more clear... */
    $smarty->assign("cn", basename($this->FAItemplatePath));
    $smarty->assign("templatePath", dirname($this->FAItemplatePath));

    $display.=  $smarty->fetch(get_template_path('faiTemplateEntry.tpl', TRUE));
    return($display);
  }

  /* Save data to object */
  function save_object()
  {
    /* Check if form is posted and we are not freezed */
    if((isset($_POST['SubObjectFormSubmitted'])) && !preg_match("/freeze/", $this->FAIstate)){

      plugin::save_object();

      /* Set user.group (FAIowner) attribute */  
      if(isset($_POST['group']) && isset($_POST["user"]) && $this->acl_is_writeable("FAIowner")){
        $this->FAIowner = $_POST["user"].'.'.$_POST["group"];
        $this->user = $_POST['user'];
        $this->group= $_POST['group'];
      }

      /* Check if permissions have changed */
      if($this->acl_is_writeable("FAImode")){

        /* Save mode */
        $tmode= "";
        foreach (array("s", "u", "g", "o") as $type){
          $nr= 1;
          $dest= 0;
          while ($nr < 5){
            if (isset($_POST["$type$nr"])){
              $dest+= $nr;
            }
            $nr+= $nr;
          }
          $tmode= $tmode.$dest;
        }
        $this->FAImode= $tmode;

        /* Assemble cn/FAItemplatePath */
        $this->cn= preg_replace('/\/+/', '/', $this->FAItemplatePath.'/'.$this->cn);
        $this->FAItemplatePath= $this->cn;
      }
    }
  }


  /* Check supplied data */
  function check()
  {
    /* Call common method to give check the hook */
    $message= plugin::check();

    if(isset($this->parent->SubObjects[$this->cn]) && $this->cn != $this->orig_cn){
      $message[] = msgPool::duplicated(_("Name"));
    }

    if(empty($this->FAItemplateFile)){
      $message[]= msgPool::required(_("File"));
    } 

    if(!preg_match('/^\//', $this->FAItemplatePath)){
      $message[]= msgPool::invalid(_("Destination path"),$this->FAItemplatePath,'/^\//');
    } 
 
    $b = basename($this->FAItemplatePath); 
    if(empty($b)){
      $message[] = msgPool::required(_("File name"));
    }

    if(empty($this->user)){
      $message[] = msgPool::required(_("User"));
    }
    elseif(preg_match("/[^0-9a-z]/i",$this->user)){
      $message[] = msgPool::invalid(_("User"),$this->user,"/[0-9a-z]/");
    }

    if(empty($this->group)){
      $message[] = msgPool::required(_("Group"));
    }
    elseif(preg_match("/[^0-9a-z]/i",$this->group)){
      $message[] = msgPool::invalid(_("Group"),$this->group,"/[0-9a-z]/");
    }

    return ($message);
  }
 
  function save()
  {
    $tmp=array();
    foreach($this->attributes as $attrs){ 
      $tmp[$attrs] = $this->$attrs;
    }

    if(($this->orig_cn)&&($tmp['cn']!=$this->orig_cn)){
      $tmp['remove']['from']  = $this->orig_cn;
      $tmp['remove']['to']    = $tmp['cn'];
    }
  
    $tmp['dn']      = $this->dn;  
    $tmp['status']  = $this->status;  

    return($tmp);
  }

  
  /* Return plugin informations for acl handling */
  static function plInfo()
  {
    return (array(
          "plShortName" => _("Template entry"),
          "plDescription" => _("FAI template entry"),
          "plSelfModify"  => FALSE,
          "plDepends"     => array(),
          "plPriority"    => 25,
          "plSection"     => array("administration"),
          "plCategory"    => array("fai"),
          "plProvidedAcls" => array(
            "cn"                => _("Name"),
            "description"       => _("Description"),
            "FAItemplateFile"   => _("Template file"),
            "FAItemplatePath"   => _("Template path"),
            "FAIowner"          => _("File owner"),
            "FAImode"           => _("File permissions"))
          ));
  }

}
// vim:tabstop=2:expandtab:shiftwidth=2:filetype=php:syntax:ruler:
?>